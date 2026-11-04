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
#include "Render/Material.h"

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
	ReleaseResources();
}

bool FSkeletalMeshViewerPanel::Init()
{
	PreviewComponent = NewObject<USkeletalMeshComponent>();
	BoneGizmoTarget = NewObject<USceneComponent>();
	PreviewComponent->SetFlags(EObjectFlags::RF_Transient);
	BoneGizmoTarget->SetFlags(EObjectFlags::RF_Transient);
	BoneGizmo.SetMode(EGizmoMode::Rotation);
	BoneGizmo.SetSpace(EGizmoSpace::Local);

	// 기본 뷰포트 크기 초기화
	ResizeViewport(800, 600);

	// 기본 상태: 닫혀 있음 (Window 메뉴 또는 Details 패널의 Edit 버튼으로 오픈)
	SetOpen(false);


	return true;
}

bool FSkeletalMeshViewerPanel::OpenAsset(const FString& Path)
{
	const FFbxAssetSet* Assets = UAssetManager::Get().LoadFbxAssets(Path);
	if (!Assets)
	{
		OpenAssetMessage = "Could not open FBX asset: " + Path;
		SetOpen(true);
		RequestFocus();
		return false;
	}
	if (Assets->SkeletalMesh)
	{
		OpenMesh(Assets->SkeletalMesh);
		return true;
	}

	OpenAssetMessage = "Imported " + std::to_string(Assets->StaticMeshes.Num()) +
		" static mesh assets. This FBX has no skeletal mesh to preview.";
	SetOpen(true);
	RequestFocus();
	return true;
}

void FSkeletalMeshViewerPanel::ReleaseResources()
{
	BoneGizmo.SetTarget(nullptr, false);
	delete PreviewComponent;
	PreviewComponent = nullptr;
	delete BoneGizmoTarget;
	BoneGizmoTarget = nullptr;
	PreviewMaterials.Reset();
	VisibleStaticMeshes.Reset();
	PreviewLighting.reset();
	PreviewLightGrid.reset();
	OpenedMeshes.Reset();
	SelectedMesh = nullptr;
	MeshToSelect = nullptr;
	RenderingInfo = {};
	ColorTarget.reset();
	DepthTarget.reset();
}

void FSkeletalMeshViewerPanel::OpenMesh(USkeletalMesh* Mesh)
{
	if (!Mesh)
		return;

	OpenAssetMessage.clear();
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
	if (SelectedMesh != Mesh)
	{
		PreviewMaterials.Reset();
		if (Mesh) PreviewMaterials.SetNum(Mesh->Materials.Num());
		SelectedMesh = Mesh;
		PreviewBounds = CalculatePreviewBounds();
	}

	if (PreviewComponent)
	{
		PreviewComponent->SetSkeletalMesh(Mesh);
		PreviewComponent->SetRelativeLocation(FVector::ZeroVector);
		PreviewComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	}

	SelectedBoneIndex = -1;
	HoveredBoneIndex = -1;
	BoneGizmo.SetTarget(nullptr, false);
	bFrameRequested = Mesh != nullptr;
	FocusOnMesh();
}

FBox FSkeletalMeshViewerPanel::CalculatePreviewBounds() const
{
	FBox Bounds{FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX)};
	if (!SelectedMesh) return Bounds;
	for (const FSkeletalMesh& Part : SelectedMesh->MeshData.Meshes)
	{
		for (const FSkeletalVertex& Vertex : Part.Vertices)
		{
			const FVector Position = Part.MeshTransform.TransformPosition(Vertex.Vertex.Position);
			Bounds.Expand(FBox{Position, Position});
		}
	}
	return Bounds;
}

FTransform FSkeletalMeshViewerPanel::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (PreviewComponent)
	{
		if (const FTransform* Transform = PreviewComponent->GetBoneLocalTransform(BoneIndex))
			return *Transform;
	}
	return FTransform{};
}

UMaterial* FSkeletalMeshViewerPanel::GetPreviewMaterial(int32 SlotIndex) const
{
	if (PreviewMaterials.IsValidIndex(SlotIndex) && PreviewMaterials[SlotIndex])
		return PreviewMaterials[SlotIndex].get();
	return SelectedMesh && SelectedMesh->Materials.IsValidIndex(SlotIndex)
		? SelectedMesh->GetMaterial(SlotIndex) : nullptr;
}

void FSkeletalMeshViewerPanel::SetPreviewSampler(int32 SlotIndex, ESamplerState Sampler)
{
	if (!SelectedMesh || !PreviewMaterials.IsValidIndex(SlotIndex)) return;
	UMaterial* Source = SelectedMesh->GetMaterial(SlotIndex);
	if (!Source) return;
	auto& Material = PreviewMaterials[SlotIndex];
	if (Source->SamplerState == Sampler)
	{
		Material.reset();
		return;
	}
	if (!Material)
	{
		Material.reset(UMaterial::CreateInstance(Source));
		if (!Material || (Source->ParamBuffer && !Material->ParamBuffer))
		{
			Material.reset();
			return;
		}
		Material->SetFlags(EObjectFlags::RF_Transient);
	}
	Material->SamplerState = Sampler;
}

bool FSkeletalMeshViewerPanel::SaveCurrentMesh()
{
	if (!SelectedMesh)
	{
		SaveStatusMessage = "No skeletal mesh selected.";
		SaveStatusTimer = 3.0f;
		return false;
	}

	// 1. PreviewComponent에서 수정한 본의 현재 LocalTransform들을 SelectedMesh->MeshData.Bones에 반영
	// 주의: Mesh.Vertices는 원본 FBX 바인드 공간 정점이므로 InverseBindPoseMat은 원본을 유지해야
	// ResetPoseToBindPose() -> UpdateSkinning() 시 정점들이 수정된 본 위치를 따라 스키닝된다.
	if (PreviewComponent)
	{
		const int32 NumBones = SelectedMesh->MeshData.Bones.Num();
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			if (const FTransform* CurTf = PreviewComponent->GetBoneLocalTransform(BoneIdx))
			{
				SelectedMesh->MeshData.Bones[BoneIdx].RefPoseTransform = *CurTf;
			}
		}
	}

	// 2. AssetManager를 통해 .bin 파일 저장 및 씬 컴포넌트(레벨 에디터 및 프리뷰) 즉시 갱신
	if (UAssetManager::Get().SaveSkeletalMesh(SelectedMesh))
	{
		if (PreviewComponent)
		{
			PreviewComponent->ResetPoseToBindPose();
		}
		PreviewBounds = CalculatePreviewBounds();
		SaveStatusMessage = "Saved to .bin successfully!";
		SaveStatusTimer = 3.5f;
		return true;
	}
	else
	{
		SaveStatusMessage = "Failed to save to .bin.";
		SaveStatusTimer = 3.5f;
		return false;
	}
}

void FSkeletalMeshViewerPanel::ResetAllBonesToBindPose()
{
	if (PreviewComponent)
	{
		PreviewComponent->ResetPoseToBindPose();
		TArray<FMatrix> BoneMatrices;
		ComputeBoneWorldMatrices(BoneMatrices);
		if (SelectedBoneIndex >= 0 && SelectedBoneIndex < BoneMatrices.Num() && BoneGizmoTarget)
		{
			const FMatrix& BoneMat = BoneMatrices[SelectedBoneIndex];
			const FVector BoneWorldPos(BoneMat.M[3][0], BoneMat.M[3][1], BoneMat.M[3][2]);
			FMatrix BoneRotMat = FMatrix::Identity;
			BoneRotMat.SetAxis(0, FVector(BoneMat.M[0][0], BoneMat.M[0][1], BoneMat.M[0][2]).GetSafeNormal());
			BoneRotMat.SetAxis(1, FVector(BoneMat.M[1][0], BoneMat.M[1][1], BoneMat.M[1][2]).GetSafeNormal());
			BoneRotMat.SetAxis(2, FVector(BoneMat.M[2][0], BoneMat.M[2][1], BoneMat.M[2][2]).GetSafeNormal());

			BoneGizmoTarget->SetRelativeLocation(BoneWorldPos);
			BoneGizmoTarget->SetRelativeRotation(MatrixToRotator(BoneRotMat));
			BoneGizmoTarget->SetRelativeScale3D(GetBoneLocalTransform(SelectedBoneIndex).Scale);
		}
		SaveStatusMessage = "Reset all bones to bind pose.";
		SaveStatusTimer = 2.5f;
	}
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

	FBox Bounds = PreviewBounds;
	if (!VisibleStaticMeshes.IsEmpty())
	{
		if (const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(SelectedMesh->GetPath()))
		{
			for (const FFbxStaticAsset& Part : Assets->StaticMeshes)
			{
				if (Part.Mesh && VisibleStaticMeshes.Find(Part.Mesh) != INDEX_NONE)
					Bounds.Expand(Part.Mesh->GetRenderBounds().GetWorldAABB(Part.NodeTransform));
			}
		}
	}
	if (Bounds.Min.X <= Bounds.Max.X)
	{
		TargetPosition = (Bounds.Min + Bounds.Max) * 0.5f;
		const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		MeshRadius = std::max(Extent.Length(), 0.001f);
		const float Aspect = ViewportHeight > 0 ? static_cast<float>(ViewportWidth) / ViewportHeight : 1.0f;
		const float HalfFov = FMath::DegreesToRadians(FovDegrees) * 0.5f;
		const float HalfHorizontalFov = std::atan(std::tan(HalfFov) * Aspect);
		CameraDistance = MeshRadius / std::sin(std::min(HalfFov, HalfHorizontalFov)) * 1.1f;
		CameraSpeed = MeshRadius * 2.5f;
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

	// 색상 렌더 타깃
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ColorTarget = RenderCommand::CreateTexture2D(Desc);

	// 깊이 렌더 타깃
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

	if (SaveStatusTimer > 0.0f)
	{
		SaveStatusTimer -= DeltaTime;
		if (SaveStatusTimer <= 0.0f)
		{
			SaveStatusMessage.clear();
		}
	}

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

	// 1. 단축키 'F': 포커스 맞추기, W/E/R/Space: 기즈모 모드 전환, Ctrl+S: 저장 (우클릭 카메라 조작 중이 아닐 때)
	if (!IO.WantTextInput && !bRightMouseDown && !IO.KeyAlt)
	{
		if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			SaveCurrentMesh();
		}
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

	// 3. 우클릭으로 1인칭 카메라를 이동하고 시점을 회전한다.
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
			CameraPitch = std::remainder(CameraPitch + IO.MouseDelta.y * 0.35f, 360.0f);

			const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
			const FQuat CurrentQuat(CurrentRot);
			CameraLocation = TargetPosition - CurrentQuat.GetForwardVector() * CameraDistance;
		}
		else if (bRightMouseDown)
		{
			const float ZoomDelta = (IO.MouseDelta.x - IO.MouseDelta.y) * std::max(CameraDistance * 0.005f, MeshRadius * 0.0001f);
			CameraDistance = std::clamp(CameraDistance - ZoomDelta, MeshRadius * 0.05f, MeshRadius * 1000.0f);

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

	// 6. 마우스 휠로 카메라를 앞뒤로 이동해 확대하거나 축소한다.
	if (std::abs(IO.MouseWheel) > 0.01f)
	{
		const float Step = std::max(CameraDistance * 0.15f, MeshRadius * 0.005f);
		CameraDistance -= IO.MouseWheel * Step;

		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentForward = CurrentQuat.GetForwardVector();

		CameraDistance = std::clamp(CameraDistance, MeshRadius * 0.05f, MeshRadius * 1000.0f);
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
	ViewInfo.SetNearZ(std::max(MeshRadius * 0.001f, 0.00001f));
	ViewInfo.SetFarZ(std::max(CameraDistance, (TargetPosition - CameraLocation).Length()) + MeshRadius * 100.0f);

	CachedViewMatrix = ViewInfo.GetViewMatrix();
	CachedProjMatrix = ViewInfo.GetPerspectiveMatrix();
}

void FSkeletalMeshViewerPanel::AppendStaticPreviewPackets(FRenderQueue& Queue, const FMatrix& PreviewWorld) const
{
	if (VisibleStaticMeshes.IsEmpty()) return;
	const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(SelectedMesh->GetPath());
	if (!Assets) return;

	for (const FFbxStaticAsset& Part : Assets->StaticMeshes)
	{
		UStaticMesh* Mesh = Part.Mesh;
		if (!Mesh || VisibleStaticMeshes.Find(Mesh) == INDEX_NONE) continue;
		const FIndexBuffer* IndexBuffer = Mesh->GetIndexBuffer(0);
		if (!Mesh->GetVertexBuffer(0) || !IndexBuffer) continue;

		// FBX 노드의 원래 배치에 프리뷰 컴포넌트의 월드 변환을 적용한다.
		const FMatrix* Model = Queue.StoreWorldMatrix(Part.NodeTransform * PreviewWorld);
		const float CameraDistance = (Model->TransformPosition(FVector::ZeroVector) - CachedCameraLocation).Length();
		const uint32 TotalIndices = IndexBuffer->GetIndexCount();
		for (const FStaticMeshSection& Section : Mesh->GetMeshData().Sections)
		{
			if (Section.IndexCount == 0 || Section.StartIndex > TotalIndices ||
				Section.IndexCount > TotalIndices - Section.StartIndex)
				continue;
			UMaterial* Material = Mesh->GetMaterial(Section.MaterialSlotIndex);
			if (!Material) continue;

			FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
			Packet.Model = Model;
			Packet.Mesh = Mesh;
			Packet.Material = Material;
			Packet.StartIndex = Section.StartIndex;
			Packet.IndexCount = Section.IndexCount;
			Packet.CameraToParticleDistance = CameraDistance;
		}
	}
}

void FSkeletalMeshViewerPanel::RenderPreview()
{
	if (!IsOpen() || !bViewportVisible || !SelectedMesh)
		return;

	if (!Renderer || !ColorTarget || !DepthTarget || ViewportWidth == 0 || ViewportHeight == 0)
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

	if (!PreviewLighting) PreviewLighting = RenderCommand::CreateConstantBuffer(sizeof(FLightingConstants));
	if (!PreviewLightGrid) PreviewLightGrid = RenderCommand::CreateConstantBuffer(sizeof(FLightGridConstants));
	const EViewMode PreviousViewMode = Renderer->GetViewMode();
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
		const FMatrix PreviewWorld = PreviewComponent->GetWorldMatrix();
		FVertexBuffer* VertexBuffer = PreviewComponent->GetSkinnedVertexBuffer();
		FIndexBuffer* IndexBuffer = SelectedMesh->IndexBuffer.get();
		if (VertexBuffer && IndexBuffer)
		{
			const FMatrix* Model = RenderQueue.StoreWorldMatrix(PreviewWorld);
			uint32 BaseIndex = 0;
			uint32 BaseMaterial = 0;
			for (const FSkeletalMesh& Part : SelectedMesh->MeshData.Meshes)
			{
				const uint32 PartStartIndex = BaseIndex;
				const uint32 PartStartMaterial = BaseMaterial;
				const uint32 PartIndexCount = static_cast<uint32>(Part.Indices.Num());
				BaseIndex += PartIndexCount;
				BaseMaterial += static_cast<uint32>(Part.MaterialSlots.Num());

				const FVector PartWorldOrigin = Model->TransformPosition(Part.MeshTransform.TransformPosition(FVector::ZeroVector));
				for (const FSkeletalMeshSection& Section : Part.Sections)
				{
					if (Section.IndexCount == 0 || Section.StartIndex > PartIndexCount ||
						Section.IndexCount > PartIndexCount - Section.StartIndex)
						continue;
					UMaterial* Material = GetPreviewMaterial(static_cast<int32>(PartStartMaterial + Section.MaterialSlotIndex));
					if (!Material) continue;

					FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
					Packet.Model = Model;
					Packet.VertexBuffer = VertexBuffer;
					Packet.IndexBuffer = IndexBuffer;
					Packet.Material = Material;
					Packet.StartIndex = PartStartIndex + Section.StartIndex;
					Packet.IndexCount = Section.IndexCount;
					Packet.CameraToParticleDistance = (PartWorldOrigin - CachedCameraLocation).Length();
				}
			}
		}

		AppendStaticPreviewPackets(RenderQueue, PreviewWorld);
		if (!RenderQueue.IsEmpty() && PreviewLighting && PreviewLightGrid)
		{
			const FLightGridConstants LightGrid{};
			RenderCommand::UpdateBufferData(PreviewLighting.get(), &LightingConstants);
			RenderCommand::UpdateBufferData(PreviewLightGrid.get(), &LightGrid);
			FViewConstants ViewConstants{};
			ViewConstants.ViewProjection = ViewProj;
			ViewConstants.CameraWorldPos = CachedCameraLocation;
			ViewConstants.CameraForward = FRotator(CameraPitch, CameraYaw, 0.0f).Quaternion().GetForwardVector();
			Renderer->RenderPreview(RenderQueue, ViewConstants, *PreviewLighting, *PreviewLightGrid);
		}

		// 3단계: 에디터와 100% 동일한 3D 기즈모(FGizmoRenderer)를 선택된 본 위치에 렌더링
		if (GizmoRenderer && BoneGizmoTarget && SelectedBoneIndex >= 0 && SelectedBoneIndex < static_cast<int32>(SelectedMesh->MeshData.Bones.Num()))
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
					BoneGizmoTarget->SetRelativeScale3D(GetBoneLocalTransform(SelectedBoneIndex).Scale);
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
	Renderer->SetViewMode(PreviousViewMode);
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bViewportVisible = false;
	bViewportHovered = false;
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
		if (!OpenAssetMessage.empty()) ImGui::TextWrapped("%s", OpenAssetMessage.c_str());

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
	ImGuiDockNodeFlags InnerDockFlags = (bHasActiveMesh && bVisible && bOpen) ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
	ImGui::DockSpace(DockSpaceID, ImVec2(0.0f, 0.0f), InnerDockFlags, &SkelViewerClass);
	ImGui::End(); // Skeletal Mesh Viewer 메인 창 끝

	if (bHasActiveMesh && bVisible && bOpen)
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
					SetSelectedMesh(OpenedMeshes.IsEmpty() ? nullptr : OpenedMeshes[0]);
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
		if (bFrameRequested)
		{
			FocusOnMesh();
			bFrameRequested = false;
		}

		if (ColorTarget && ColorTarget->GetSRV())
		{
			const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
			const ImVec2 ImageSize(static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight));
			bViewportVisible = true;
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

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
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

		const FTransform* Current = PreviewComponent ? PreviewComponent->GetBoneLocalTransform(Idx) : nullptr;
		const FTransform& LocalTf = Current ? *Current : Bones[Idx].RefPoseTransform;
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

bool FSkeletalMeshViewerPanel::IsBoneAncestorOf(int32 AncestorIndex, int32 TargetBoneIndex, const TArray<FSkeletalBone>& Bones) const
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

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
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
			BoneGizmoTarget->SetRelativeScale3D(GetBoneLocalTransform(SelectedBoneIndex).Scale);
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
				FTransform NewLocalTf = GetBoneLocalTransform(SelectedBoneIndex);
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

					// 행벡터 규약에서 월드 회전에 부모 회전의 전치 행렬을 곱하면 로컬 회전이 된다.
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
			// 선 표시 모드
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
			if (SelectedMesh)
			{
				if (ImGui::MenuItem("Save Asset (.bin)", "Ctrl+S"))
				{
					SaveCurrentMesh();
				}
				if (ImGui::MenuItem("Reset All Bones to Bind Pose"))
				{
					ResetAllBonesToBindPose();
				}
				ImGui::Separator();
			}

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

		// 4. 빠른 저장 버튼 및 상태 표시
		if (SelectedMesh)
		{
			ImGui::Separator();
			if (ImGui::Button("Save Asset"))
			{
				SaveCurrentMesh();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Save modified bone transforms to .bin file (Ctrl+S)");
			}

			if (SaveStatusTimer > 0.0f)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", SaveStatusMessage.c_str());
			}
		}

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
	int32 VertexCount = 0;
	int32 IndexCount = 0;
	for (const FSkeletalMesh& Part : Data.Meshes)
	{
		VertexCount += Part.Vertices.Num();
		IndexCount += Part.Indices.Num();
	}
	ImGui::Text("Vertices: %d", VertexCount);
	ImGui::Text("Triangles: %d", IndexCount / 3);
	ImGui::Text("Sections: %d", SelectedMesh->Sections.Num());
	ImGui::Text("Material Slots: %d", SelectedMesh->Materials.Num());

	const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(Path);
	if (Assets && !Assets->StaticMeshes.IsEmpty() &&
		ImGui::CollapsingHeader("Separate Static Meshes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const FFbxStaticAsset& Part : Assets->StaticMeshes)
		{
			if (!Part.Mesh) continue;
			const FString& AssetPath = Part.Mesh->GetPath();
			const FString AssetName = AssetPath.substr(AssetPath.find_last_of('/') + 1);
			const int32 VisibleIndex = VisibleStaticMeshes.Find(Part.Mesh);
			bool bVisible = VisibleIndex != INDEX_NONE;
			ImGui::PushID(Part.Mesh);
			if (ImGui::Checkbox("##ShowStaticMesh", &bVisible))
			{
				if (bVisible)
					VisibleStaticMeshes.Add(Part.Mesh);
				else
					VisibleStaticMeshes.RemoveAt(VisibleIndex);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(AssetName.c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", AssetPath.c_str());
			ImGui::PopID();
		}
	}

	const FBox& Bounds = PreviewBounds;
	const FVector Size = Bounds.Max - Bounds.Min;
	ImGui::Text("Bounds Size: (%.2f, %.2f, %.2f)", Size.X, Size.Y, Size.Z);

	if (ImGui::CollapsingHeader("Material Slots", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const char* Samplers[] = { "Clamp", "Wrap" };
		int32 SlotIndex = 0;
		for (const FSkeletalMesh& Part : Data.Meshes)
		{
			for (const FSkeletalMaterialSlot& Slot : Part.MaterialSlots)
			{
				ImGui::PushID(SlotIndex);
				ImGui::BulletText("[%d] %s", SlotIndex, Slot.Name.c_str());
				if (!Slot.BaseColorTexturePath.empty())
					ImGui::TextWrapped("Diffuse: %s", Slot.BaseColorTexturePath.c_str());

				if (const UMaterial* Material = GetPreviewMaterial(SlotIndex))
				{
					int Mode = Material->SamplerState == ESamplerState::LinearWrap ? 1 : 0;
					if (ImGui::Combo("Sampler", &Mode, Samplers, IM_ARRAYSIZE(Samplers)))
						SetPreviewSampler(SlotIndex, Mode == 1 ? ESamplerState::LinearWrap : ESamplerState::LinearClamp);
				}
				ImGui::PopID();
				++SlotIndex;
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "--- Pose Actions ---");
	if (ImGui::Button("Save Mesh (Apply Ref Pose to .bin)", ImVec2(-1.0f, 26.0f)))
	{
		SaveCurrentMesh();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Save current modified bone poses as new bind pose and bake to .bin (Ctrl+S)");
	}

	if (ImGui::Button("Reset All Bones to Bind Pose", ImVec2(-1.0f, 24.0f)))
	{
		ResetAllBonesToBindPose();
	}

	if (SaveStatusTimer > 0.0f)
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", SaveStatusMessage.c_str());
	}
}

void FSkeletalMeshViewerPanel::DrawBoneNode(int32 BoneIndex, const TArray<FSkeletalBone>& Bones)
{
	if (BoneIndex < 0 || BoneIndex >= Bones.Num())
		return;

	const FSkeletalBone& Bone = Bones[BoneIndex];

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

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
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
		? ImGui::GetFrameHeightWithSpacing() * 10.6f
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
		const FSkeletalBone& SelBone = Bones[SelectedBoneIndex];
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
			FTransform LocalTf = GetBoneLocalTransform(SelectedBoneIndex);
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

		if (ImGui::Button("Reset Bone", ImVec2(-1.0f, 0.0f)))
		{
			if (PreviewComponent)
			{
				PreviewComponent->SetBoneLocalTransform(SelectedBoneIndex, SelBone.RefPoseTransform);
			}
		}
		if (ImGui::Button("Focus Bone", ImVec2(-1.0f, 0.0f)))
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
		if (ImGui::Button("Deselect", ImVec2(-1.0f, 0.0f)))
		{
			SelectedBoneIndex = -1;
			BoneGizmo.SetTarget(nullptr, false);
		}
		ImGui::Spacing();
		if (ImGui::Button("Save Mesh (Ctrl+S)", ImVec2(-1.0f, 24.0f)))
		{
			SaveCurrentMesh();
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
	}

	if (ImGui::CollapsingHeader("Skeleton Display", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Show Bones", &bShowBones);

		ImGui::Text("Bone Style:");
		ImGui::SameLine();
		if (ImGui::RadioButton("3D Cone", bDrawBoneCones))
		{
			bDrawBoneCones = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Line", !bDrawBoneCones))
		{
			bDrawBoneCones = false;
		}

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
