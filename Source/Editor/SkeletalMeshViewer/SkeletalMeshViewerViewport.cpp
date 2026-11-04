#include "EnginePCH.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerViewport.h"
#include "Editor/HitoriEd/EditorViewportClient.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "UObject/UObjectGlobals.h"
#include <algorithm>
#include <cmath>

FSkeletalMeshViewerViewport::FSkeletalMeshViewerViewport()
{
	BoneGizmo.SetMode(EGizmoMode::Rotation);
	BoneGizmo.SetSpace(EGizmoSpace::Local);
}

FSkeletalMeshViewerViewport::~FSkeletalMeshViewerViewport()
{
	ReleaseResources();
}

void FSkeletalMeshViewerViewport::SetMesh(USkeletalMesh* InMesh)
{
	CancelGizmoDrag();
	Mesh = InMesh;
	if (!PreviewComponent)
	{
		PreviewComponent = NewObject<USkeletalMeshComponent>();
		PreviewComponent->SetFlags(EObjectFlags::RF_Transient);
		GizmoTarget = NewObject<USceneComponent>();
		GizmoTarget->SetFlags(EObjectFlags::RF_Transient);
	}
	// 같은 에셋을 다시 열어도 기준 자세로 시작한다.
	PreviewComponent->SetSkeletalMesh(nullptr);
	PreviewComponent->SetSkeletalMesh(Mesh);
	SelectedBoneIndex = -1;
	BoneGizmo.SetTarget(nullptr);
	RebuildBoneTransforms();
	bHasRenderedView = false;
	CameraYaw = 0.0f;
	CameraPitch = 10.0f;
	// 첫 UI 레이아웃의 실제 종횡비가 들어온 다음 화면에 맞춘다.
	bFrameRequested = true;
}

void FSkeletalMeshViewerViewport::SetSize(uint32 InWidth, uint32 InHeight)
{
	InWidth = std::clamp(InWidth, 1u, 4096u);
	InHeight = std::clamp(InHeight, 1u, 4096u);
	if (Width == InWidth && Height == InHeight) return;
	Width = InWidth;
	Height = InHeight;
	bSizeDirty = true;
}

void FSkeletalMeshViewerViewport::FrameMesh()
{
	if (!Mesh) return;
	FBox Bounds{};
	bool bHasBounds = false;
	// 파트별 MeshTransform을 적용한 공통 공간에서 메시와 본을 함께 표시한다.
	for (const FSkeletalMesh& Part : Mesh->MeshData.Meshes)
	{
		for (const FSkeletalVertex& Vertex : Part.Vertices)
		{
			const FVector Position = Part.MeshTransform.TransformPosition(Vertex.Vertex.Position);
			const FBox PointBounds{Position, Position};
			if (!bHasBounds) { Bounds = PointBounds; bHasBounds = true; }
			else Bounds.Expand(PointBounds);
		}
	}
	if (!bHasBounds) return;
	CameraTarget = (Bounds.Min + Bounds.Max) * 0.5f;
	MeshRadius = std::max((Bounds.Max - Bounds.Min).Length() * 0.5f, 0.01f);
	const float HalfVerticalFov = FMath::DegreesToRadians(60.0f) * 0.5f;
	const float Aspect = static_cast<float>(Width) / static_cast<float>(Height);
	const float HalfHorizontalFov = std::atan(std::tan(HalfVerticalFov) * Aspect);
	CameraDistance = MeshRadius / std::sin(std::min(HalfVerticalFov, HalfHorizontalFov)) * 1.1f;
	bFrameRequested = false;
}

void FSkeletalMeshViewerViewport::Orbit(float DeltaX, float DeltaY)
{
	CameraYaw = std::remainder(CameraYaw + DeltaX * 0.3f, 360.0f);
	CameraPitch = std::clamp(CameraPitch + DeltaY * 0.3f, -85.0f, 85.0f);
}

void FSkeletalMeshViewerViewport::Zoom(float WheelSteps)
{
	CameraDistance = std::clamp(CameraDistance * std::pow(0.9f, WheelSteps), MeshRadius * 0.05f, MeshRadius * 1000.0f);
}

void FSkeletalMeshViewerViewport::ResizeTargets()
{
	// UI가 크기만 요청하고, 실제 GPU 자원 교체는 다음 렌더 시점에 한다.
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ColorTarget = RenderCommand::CreateTexture2D(Desc);
	Desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	DepthTarget = RenderCommand::CreateTexture2D(Desc);

	RenderingInfo = {};
	RenderingInfo.ViewportSetting.Width = Width;
	RenderingInfo.ViewportSetting.Height = Height;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = ColorTarget.get();
	ColorDesc.ClearValue.ColorClearValue = {0.06f, 0.07f, 0.09f, 1.0f};
	RenderingInfo.ColorRenderTargets.Add(ColorDesc);
	RenderingInfo.DepthStencil.Texture = DepthTarget.get();
	bSizeDirty = false;
}

void FSkeletalMeshViewerViewport::SetAmbientIntensity(float Value)
{
	AmbientIntensity = std::clamp(Value, 0.0f, 3.0f);
}

void FSkeletalMeshViewerViewport::RebuildBoneTransforms()
{
	BoneGlobalMatrices.Reset();
	BoneGlobalRotations.Reset();
	if (!Mesh || !PreviewComponent) return;
	for (int32 Index = 0; Index < Mesh->MeshData.Bones.Num(); ++Index)
	{
		const FTransform* Local = PreviewComponent->GetBoneLocalTransform(Index);
		const int32 Parent = Mesh->MeshData.Bones[Index].ParentIndex;
		if (!Local || Parent < -1 || Parent >= Index)
		{
			BoneGlobalMatrices.Reset();
			BoneGlobalRotations.Reset();
			BoneGizmo.SetTarget(nullptr);
			return;
		}
		FMatrix Global = Local->GetLocalMatrix();
		FQuat Rotation = Local->GetOrientation();
		if (Parent != -1)
		{
			Global = Global * BoneGlobalMatrices[Parent];
			Rotation = (BoneGlobalRotations[Parent] * Rotation).Normalized();
		}
		BoneGlobalMatrices.Add(Global);
		BoneGlobalRotations.Add(Rotation);
	}
}

void FSkeletalMeshViewerViewport::SetSelectedBoneIndex(int32 BoneIndex)
{
	CancelGizmoDrag();
	SelectedBoneIndex = BoneGlobalMatrices.IsValidIndex(BoneIndex) ? BoneIndex : -1;
	SyncGizmoTarget();
}

const FTransform* FSkeletalMeshViewerViewport::GetSelectedBoneTransform() const
{
	return PreviewComponent ? PreviewComponent->GetBoneLocalTransform(SelectedBoneIndex) : nullptr;
}

bool FSkeletalMeshViewerViewport::SetSelectedBoneTransform(const FTransform& Transform)
{
	if (!PreviewComponent || !PreviewComponent->SetBoneLocalTransform(SelectedBoneIndex, Transform)) return false;
	RebuildBoneTransforms();
	if (!BoneGizmo.IsUsing()) SyncGizmoTarget();
	return true;
}

void FSkeletalMeshViewerViewport::SyncGizmoTarget()
{
	const FTransform* Local = GetSelectedBoneTransform();
	if (!GizmoTarget || !Local || !BoneGlobalMatrices.IsValidIndex(SelectedBoneIndex))
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}
	FTransform TargetTransform;
	TargetTransform.Location = BoneGlobalMatrices[SelectedBoneIndex].TransformPosition(FVector::ZeroVector);
	TargetTransform.Rotation = BoneGlobalRotations[SelectedBoneIndex].ToFRotator();
	// 프록시는 부모에 부착하지 않는다. Scale은 선택 본의 로컬 배율로 표시/편집한다.
	TargetTransform.Scale = Local->Scale;
	GizmoTarget->SetTransform(TargetTransform);
	BoneGizmo.SetTarget(GizmoTarget);
}

void FSkeletalMeshViewerViewport::ApplyGizmoTransform()
{
	const FTransform* Current = GetSelectedBoneTransform();
	if (!Current || !GizmoTarget || !Mesh) return;
	FTransform Local = *Current;
	const int32 Parent = Mesh->MeshData.Bones[SelectedBoneIndex].ParentIndex;
	switch (BoneGizmo.GetMode())
	{
	case EGizmoMode::Location:
		if (Parent != -1 && std::abs(BoneGlobalMatrices[Parent].Determinant()) < 1.0e-12f) return;
		Local.Location = Parent == -1 ? GizmoTarget->GetWorldLocation()
			: BoneGlobalMatrices[Parent].Inverse().TransformPosition(GizmoTarget->GetWorldLocation());
		break;
	case EGizmoMode::Rotation:
		Local.Rotation = (Parent == -1 ? GizmoTarget->GetRelativeRotationQuat()
			: BoneGlobalRotations[Parent].Inverse() * GizmoTarget->GetRelativeRotationQuat()).Normalized().ToFRotator();
		break;
	case EGizmoMode::Scale:
		Local.Scale = GizmoTarget->GetRelativeScale3D();
		break;
	}
	SetSelectedBoneTransform(Local);
}

void FSkeletalMeshViewerViewport::ResetPose()
{
	if (!PreviewComponent || !Mesh) return;
	CancelGizmoDrag();
	PreviewComponent->SetSkeletalMesh(nullptr);
	PreviewComponent->SetSkeletalMesh(Mesh);
	RebuildBoneTransforms();
	SyncGizmoTarget();
}

void FSkeletalMeshViewerViewport::SetGizmoMode(EGizmoMode Mode)
{
	CancelGizmoDrag();
	BoneGizmo.SetMode(Mode);
	SyncGizmoTarget();
}

void FSkeletalMeshViewerViewport::CancelGizmoDrag()
{
	BoneGizmo.EndDrag();
}

bool FSkeletalMeshViewerViewport::ProjectBone(int32 BoneIndex, const FVector2& DisplaySize,
	FVector2& OutPosition, float& OutDepth) const
{
	if (!bHasRenderedView || !BoneGlobalMatrices.IsValidIndex(BoneIndex)) return false;
	const FVector Position = BoneGlobalMatrices[BoneIndex].TransformPosition(FVector::ZeroVector);
	const FVector4 Clip = FVector4(Position.X, Position.Y, Position.Z, 1.0f) * RenderedViewProjection;
	if (Clip.W <= 0.0f) return false;
	const float X = Clip.X / Clip.W;
	const float Y = Clip.Y / Clip.W;
	OutDepth = Clip.Z / Clip.W;
	if (X < -1.0f || X > 1.0f || Y < -1.0f || Y > 1.0f || OutDepth < 0.0f || OutDepth > 1.0f) return false;
	OutPosition = {(X * 0.5f + 0.5f) * DisplaySize.X, (0.5f - Y * 0.5f) * DisplaySize.Y};
	return true;
}

int32 FSkeletalMeshViewerViewport::PickBone(const FVector2& MousePosition) const
{
	if (!Mesh || !ColorTarget) return -1;
	const FVector2 Size{static_cast<float>(ColorTarget->GetWidth()), static_cast<float>(ColorTarget->GetHeight())};
	int32 Best = -1;
	float BestDistance = 10.0f * 10.0f;
	float BestDepth = 1.0f;
	for (int32 Index = 0; Index < BoneGlobalMatrices.Num(); ++Index)
	{
		FVector2 Point;
		float Depth;
		if (!ProjectBone(Index, Size, Point, Depth)) continue;
		const FVector2 Delta = MousePosition - Point;
		const float Distance = Delta.X * Delta.X + Delta.Y * Delta.Y;
		if (Distance < BestDistance || (std::abs(Distance - BestDistance) < 0.1f && Depth < BestDepth))
		{
			Best = Index;
			BestDistance = Distance;
			BestDepth = Depth;
		}
	}
	return Best;
}

void FSkeletalMeshViewerViewport::UpdateInteraction(const FVector2& MousePosition,
	bool bHovered, bool bLeftClicked, bool bLeftDown, bool bPickBones)
{
	if (!bHasRenderedView || !ColorTarget) return;
	const FRect Rect{0, 0, static_cast<float>(ColorTarget->GetWidth()), static_cast<float>(ColorTarget->GetHeight())};
	const FRay Ray = FEditorViewportClient::DeprojectView(RenderedCamera, Rect, MousePosition);
	if (BoneGizmo.IsUsing())
	{
		BoneGizmo.Update(Ray, MousePosition, GizmoViewProjection, static_cast<int>(Rect.Width),
			static_cast<int>(Rect.Height), bLeftDown, RenderedCamera.Transform.Location, false);
		if (bLeftDown) ApplyGizmoTransform();
		else SyncGizmoTarget();
		return;
	}
	const FVector2 HoverPosition = bHovered ? MousePosition : FVector2(-100000.0f, -100000.0f);
	FVector2 SelectedPoint;
	float SelectedDepth;
	USceneComponent* Target = BoneGizmo.GetTarget();
	if (!ProjectBone(SelectedBoneIndex, FVector2(Rect.Width, Rect.Height), SelectedPoint, SelectedDepth))
		BoneGizmo.SetTarget(nullptr);
	BoneGizmo.Update(Ray, HoverPosition, GizmoViewProjection, static_cast<int>(Rect.Width),
		static_cast<int>(Rect.Height), false, RenderedCamera.Transform.Location, false);
	BoneGizmo.SetTarget(Target);
	if (!bHovered || !bLeftClicked) return;
	if (BoneGizmo.GetHoveredAxis() >= 0)
	{
		BoneGizmo.Update(Ray, MousePosition, GizmoViewProjection, static_cast<int>(Rect.Width),
			static_cast<int>(Rect.Height), true, RenderedCamera.Transform.Location, false);
	}
	else if (bPickBones) SetSelectedBoneIndex(PickBone(MousePosition));
}

void FSkeletalMeshViewerViewport::Render(FRenderer& Renderer)
{
	if (!Mesh) return;
	if (bSizeDirty) ResizeTargets();
	if (bFrameRequested) FrameMesh();
	if (!ColorTarget || !DepthTarget || !ColorTarget->GetRTV() || !DepthTarget->GetDSV()) return;

	FViewCamera Camera{};
	Camera.Transform.Rotation = FQuat::MakeFromEuler(0.0f,
		FMath::DegreesToRadians(CameraPitch), FMath::DegreesToRadians(CameraYaw));
	Camera.Transform.Location = CameraTarget - Camera.Transform.Rotation.GetForwardVector() * CameraDistance;
	Camera.Projection.Mode = EProjectionMode::Perspective;
	Camera.Projection.FovDegrees = 60.0f;
	Camera.Projection.NearClip = std::max(MeshRadius * 0.001f, 0.0001f);
	Camera.Projection.FarClip = CameraDistance + MeshRadius * 4.0f;
	const FRect Rect{0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height)};
	const FMatrix ViewProjection = FEditorViewportClient::CalcViewProjection(Camera, Rect);
	RenderedCamera = Camera;
	RenderedViewProjection = ViewProjection;
	// 기존 Gizmo는 카메라 앞 10유닛에 그린다. 모델 크기와 무관한 클립 범위를 사용한다.
	FViewCamera GizmoCamera = Camera;
	GizmoCamera.Projection.NearClip = 0.01f;
	GizmoCamera.Projection.FarClip = std::max(Camera.Projection.FarClip, 100.0f);
	GizmoViewProjection = FEditorViewportClient::CalcViewProjection(GizmoCamera, Rect);
	bHasRenderedView = true;

	FRenderQueue Queue;
	FVertexBuffer* SkinnedBuffer = PreviewComponent ? PreviewComponent->GetSkinnedVertexBuffer() : nullptr;
	if (SkinnedBuffer && Mesh->IndexBuffer)
	{
		uint32 BaseIndex = 0;
		uint32 BaseMaterial = 0;
		for (const FSkeletalMesh& Part : Mesh->MeshData.Meshes)
		{
			for (const FSkeletalMeshSection& Section : Part.Sections)
			{
				if (Section.IndexCount == 0 || Section.StartIndex > static_cast<uint32>(Part.Indices.Num())
					|| Section.IndexCount > static_cast<uint32>(Part.Indices.Num()) - Section.StartIndex) continue;
				UMaterial* Material = Mesh->GetMaterial(BaseMaterial + Section.MaterialSlotIndex);
				if (!Material) continue;
				FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
				Packet.Model = &Part.MeshTransform;
				Packet.VertexBuffer = SkinnedBuffer;
				Packet.IndexBuffer = Mesh->IndexBuffer.get();
				Packet.Material = Material;
				Packet.StartIndex = BaseIndex + Section.StartIndex;
				Packet.IndexCount = Section.IndexCount;
				Packet.CameraToParticleDistance = (Part.MeshTransform.TransformPosition(FVector::ZeroVector)
					- Camera.Transform.Location).Length();
			}
			BaseIndex += static_cast<uint32>(Part.Indices.Num());
			BaseMaterial += static_cast<uint32>(Part.MaterialSlots.Num());
		}
	}

	if (!PreviewLighting) PreviewLighting = RenderCommand::CreateConstantBuffer(sizeof(FLightingConstants));
	if (!PreviewLightGrid) PreviewLightGrid = RenderCommand::CreateConstantBuffer(sizeof(FLightGridConstants));
	if (!GizmoRenderer)
	{
		GizmoRenderer = std::make_unique<FGizmoRenderer>();
		GizmoRenderer->Init(&Renderer);
	}
	if (!PreviewLighting || !PreviewLightGrid) return;
	FLightingConstants Lighting{};
	Lighting.Ambient.Color = FVector::OneVector;
	Lighting.Ambient.Intensity = AmbientIntensity;
	Lighting.Directional.Color = FVector::OneVector;
	Lighting.Directional.Intensity = 0.45f;
	Lighting.Directional.Direction = Camera.Transform.Rotation.GetForwardVector();
	FLightGridConstants LightGrid{};
	RenderCommand::UpdateBufferData(PreviewLighting.get(), &Lighting);
	RenderCommand::UpdateBufferData(PreviewLightGrid.get(), &LightGrid);
	FViewConstants ViewConstants{};
	ViewConstants.ViewProjection = ViewProjection;
	ViewConstants.CameraWorldPos = Camera.Transform.Location;
	ViewConstants.CameraForward = Camera.Transform.Rotation.GetForwardVector();
	const EViewMode PreviousViewMode = Renderer.GetViewMode();
	Renderer.SetViewMode(EViewMode::Lit_Lambert);
	RenderCommand::BeginRenderPass(RenderingInfo);
	RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	Renderer.RenderPreview(Queue, ViewConstants, *PreviewLighting, *PreviewLightGrid);
	FVector2 SelectedScreenPoint;
	float SelectedDepth;
	if (GizmoRenderer && BoneGizmo.GetTarget()
		&& ProjectBone(SelectedBoneIndex, FVector2(static_cast<float>(Width), static_cast<float>(Height)), SelectedScreenPoint, SelectedDepth))
	{
		RenderCommand::ClearDepthStencil(DepthTarget.get());
		GizmoRenderer->OnRender(BoneGizmo, GizmoViewProjection, Camera.Transform.Location, false);
	}
	RenderCommand::EndRenderPass(RenderingInfo);
	Renderer.SetViewMode(PreviousViewMode);
}

void FSkeletalMeshViewerViewport::ReleaseResources()
{
	CancelGizmoDrag();
	BoneGizmo.SetTarget(nullptr);
	delete GizmoTarget;
	GizmoTarget = nullptr;
	delete PreviewComponent;
	PreviewComponent = nullptr;
	GizmoRenderer.reset();
	PreviewLighting.reset();
	PreviewLightGrid.reset();
	BoneGlobalMatrices.Reset();
	BoneGlobalRotations.Reset();
	SelectedBoneIndex = -1;
	bHasRenderedView = false;
	RenderingInfo = {};
	ColorTarget.reset();
	DepthTarget.reset();
	Mesh = nullptr;
	bSizeDirty = true;
}
