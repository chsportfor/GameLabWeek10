#include "EnginePCH.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerPanel.h"
#include "Asset/AssetManager.h"
#include "Render/Mesh.h"
#include <algorithm>

bool FSkeletalMeshViewerPanel::Init()
{
	SetOpen(false);
	return true;
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
	(void)DeltaTime;
}

bool FSkeletalMeshViewerPanel::OpenAsset(const FString& Path)
{
	SetOpen(true);
	bRequestFocus = true;
	USkeletalMesh* Mesh = UAssetManager::Get().LoadFbxSkeletonMesh(Path);
	if (!Mesh)
	{
		LoadError = "Could not open: " + Path;
		return false;
	}
	LoadError.clear();
	AssetPath = Path;
	Viewport.SetMesh(Mesh);
	RebuildHierarchy();
	return true;
}

void FSkeletalMeshViewerPanel::SetSelectedBoneIndex(int32 BoneIndex)
{
	Viewport.SetSelectedBoneIndex(BoneIndex);
}

void FSkeletalMeshViewerPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
	if (bRequestFocus)
	{
		ImGui::SetNextWindowFocus();
		bRequestFocus = false;
	}
	bool bOpen = IsOpen();
	bPreviewVisible = false;
	const bool bDrawContents = ImGui::Begin(GetPanelName(), &bOpen);
	SetOpen(bOpen);
	if (bDrawContents)
	{
		if (!LoadError.empty()) ImGui::TextWrapped("%s", LoadError.c_str());
		if (GetMesh())
		{
			if (ImGui::Button("Frame Mesh")) Viewport.FrameMesh();
			ImGui::SameLine();
			ImGui::TextUnformatted("RMB drag: orbit | Wheel: zoom | F: frame");
			float Ambient = Viewport.GetAmbientIntensity();
			ImGui::SetNextItemWidth(160.0f);
			if (ImGui::SliderFloat("Ambient", &Ambient, 0.0f, 3.0f, "%.2f")) Viewport.SetAmbientIntensity(Ambient);
			ImGui::SameLine();
			ImGui::Checkbox("Show Bones", &bShowBones);
			ImGui::SameLine();
			ImGui::BeginDisabled(Viewport.IsDraggingGizmo());
			if (ImGui::RadioButton("Move", Viewport.GetGizmoMode() == EGizmoMode::Location)) Viewport.SetGizmoMode(EGizmoMode::Location);
			ImGui::SameLine();
			if (ImGui::RadioButton("Rotate", Viewport.GetGizmoMode() == EGizmoMode::Rotation)) Viewport.SetGizmoMode(EGizmoMode::Rotation);
			ImGui::SameLine();
			if (ImGui::RadioButton("Scale", Viewport.GetGizmoMode() == EGizmoMode::Scale)) Viewport.SetGizmoMode(EGizmoMode::Scale);
			ImGui::SameLine();
			if (ImGui::Button("Reset Pose")) Viewport.ResetPose();
			ImGui::EndDisabled();
			ImGui::TextWrapped("%s", AssetPath.c_str());
			ImGui::Separator();
			if (ImGui::BeginTable("ViewerLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 220.0f);
				ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 220.0f);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::BeginChild("HierarchyRegion");
				DrawHierarchy();
				ImGui::EndChild();
				ImGui::TableNextColumn();
				ImGui::BeginChild("PreviewRegion", ImVec2(0, 0), ImGuiChildFlags_None,
					ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				DrawViewport();
				ImGui::EndChild();
				ImGui::TableNextColumn();
				ImGui::BeginChild("DetailsRegion");
				DrawDetails();
				ImGui::EndChild();
				ImGui::EndTable();
			}
		}
		else ImGui::TextUnformatted("Double-click an FBX in Content Drawer to open it here.");
	}
	if (!bPreviewVisible || !bOpen) Viewport.CancelGizmoDrag();
	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawHierarchy()
{
	ImGui::TextUnformatted("Bone Hierarchy");
	ImGui::Separator();
	const USkeletalMesh* Mesh = GetMesh();
	if (!Mesh || Mesh->MeshData.Bones.IsEmpty())
	{
		ImGui::TextUnformatted("No bones in this mesh.");
		return;
	}
	if (BoneChildren.Num() != Mesh->MeshData.Bones.Num()) RebuildHierarchy();
	if (ImGui::Button("Expand All")) std::fill(ExpandedBones.begin(), ExpandedBones.end(), uint8{1});
	ImGui::SameLine();
	if (ImGui::Button("Collapse All")) std::fill(ExpandedBones.begin(), ExpandedBones.end(), uint8{0});
	const int32 Selected = GetSelectedBoneIndex();
	bool bRevealSelected = Selected != LastHierarchySelectedBone;
	ImGui::BeginDisabled(!HierarchyParents.IsValidIndex(Selected));
	if (ImGui::Button("Find Selected")) bRevealSelected = true;
	ImGui::EndDisabled();
	if (bRevealSelected)
	{
		// 화면 피킹/오른쪽 목록에서 선택한 본도 접힌 부모들을 열어 찾아준다.
		for (int32 Parent = HierarchyParents.IsValidIndex(Selected) ? HierarchyParents[Selected] : -1;
			Parent != -1; Parent = HierarchyParents[Parent])
			ExpandedBones[Parent] = 1;
	}
	LastHierarchySelectedBone = Selected;
	if (InvalidParentCount > 0)
		ImGui::TextWrapped("%d bones have invalid parents and are shown as roots.", InvalidParentCount);
	ImGui::BeginChild("BoneTree", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushID(Mesh);
	ImGui::BeginDisabled(Viewport.IsDraggingGizmo());
	for (int32 Root : RootBones) DrawBoneNode(Root, bRevealSelected);
	ImGui::EndDisabled();
	ImGui::PopID();
	ImGui::EndChild();
}

void FSkeletalMeshViewerPanel::RebuildHierarchy()
{
	BoneChildren.Reset();
	RootBones.Reset();
	HierarchyParents.Reset();
	ExpandedBones.Reset();
	LastHierarchySelectedBone = -1;
	InvalidParentCount = 0;
	const USkeletalMesh* Mesh = GetMesh();
	if (!Mesh) return;
	const auto& Bones = Mesh->MeshData.Bones;
	BoneChildren.SetNum(Bones.Num());
	HierarchyParents.SetNum(Bones.Num());
	ExpandedBones.SetNum(Bones.Num());
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const int32 Parent = Bones[Index].ParentIndex;
		// Importer와 CPU Skinning의 부모 우선 배열 규약을 그대로 사용한다.
		// 잘못된 연결은 UI에서 루트로 표시해 순환 재귀/범위 밖 접근을 막는다.
		if (Parent >= 0 && Parent < Index)
		{
			HierarchyParents[Index] = Parent;
			BoneChildren[Parent].Add(Index);
		}
		else
		{
			HierarchyParents[Index] = -1;
			RootBones.Add(Index);
			ExpandedBones[Index] = 1;
			if (Parent != -1) ++InvalidParentCount;
		}
	}
}

void FSkeletalMeshViewerPanel::DrawBoneNode(int32 BoneIndex, bool bScrollToSelection)
{
	const auto& Bone = GetMesh()->MeshData.Bones[BoneIndex];
	const bool bHasChildren = !BoneChildren[BoneIndex].IsEmpty();
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DrawLinesToNodes;
	if (GetSelectedBoneIndex() == BoneIndex) Flags |= ImGuiTreeNodeFlags_Selected;
	if (!bHasChildren) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	ImGui::PushID(BoneIndex);
	if (bHasChildren) ImGui::SetNextItemOpen(ExpandedBones[BoneIndex] != 0, ImGuiCond_Always);
	const bool bOpen = ImGui::TreeNodeEx("BoneNode", Flags, "%s", Bone.Name.c_str());
	ExpandedBones[BoneIndex] = bOpen ? 1 : 0;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		SetSelectedBoneIndex(BoneIndex);
		// 이미 보이는 행을 직접 클릭했으므로 다음 프레임 자동 스크롤은 하지 않는다.
		LastHierarchySelectedBone = GetSelectedBoneIndex();
	}
	if (bScrollToSelection && GetSelectedBoneIndex() == BoneIndex) ImGui::SetScrollHereY(0.5f);
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::TextUnformatted(Bone.Name.c_str());
		ImGui::Text("Index: %d | Parent: %d", BoneIndex, Bone.ParentIndex);
		ImGui::EndTooltip();
	}
	if (bOpen && bHasChildren)
	{
		for (int32 Child : BoneChildren[BoneIndex]) DrawBoneNode(Child, bScrollToSelection);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FSkeletalMeshViewerPanel::DrawViewport()
{
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f) return;
	bPreviewVisible = true;
	Viewport.SetSize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	if (FTexture2D* Target = Viewport.GetColorTarget(); Target && Target->GetSRV())
		ImGui::Image((ImTextureID)Target->GetSRV(), Size);
	else
		ImGui::InvisibleButton("PreviewPending", Size);

	const bool bHovered = ImGui::IsItemHovered();
	if (bShowBones) DrawBoneOverlay(Origin, Size);
	// 입력은 이 미리보기 영역에서만 소비한다. 레벨 카메라 상태는 참조하지 않는다.
	if (bHovered && !Viewport.IsDraggingGizmo())
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) Viewport.Orbit(IO.MouseDelta.x, IO.MouseDelta.y);
		if (IO.MouseWheel != 0.0f) Viewport.Zoom(IO.MouseWheel);
		if (!IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) Viewport.FrameMesh();
	}
	if (FTexture2D* Target = Viewport.GetColorTarget())
	{
		const ImVec2 Mouse = ImGui::GetMousePos();
		const FVector2 LocalMouse{(Mouse.x - Origin.x) * Target->GetWidth() / Size.x,
			(Mouse.y - Origin.y) * Target->GetHeight() / Size.y};
		Viewport.UpdateInteraction(LocalMouse, bHovered, ImGui::IsMouseClicked(ImGuiMouseButton_Left),
			ImGui::IsMouseDown(ImGuiMouseButton_Left), bShowBones);
	}
}

void FSkeletalMeshViewerPanel::DrawBoneOverlay(const ImVec2& Origin, const ImVec2& Size)
{
	const USkeletalMesh* Mesh = GetMesh();
	if (!Mesh) return;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const FVector2 DisplaySize{Size.x, Size.y};
	const int32 SelectedBoneIndex = GetSelectedBoneIndex();
	DrawList->PushClipRect(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), true);
	for (int32 Index = 0; Index < Mesh->MeshData.Bones.Num(); ++Index)
	{
		FVector2 Point;
		float Depth;
		if (!Viewport.ProjectBone(Index, DisplaySize, Point, Depth)) continue;
		const ImVec2 Screen{Origin.x + Point.X, Origin.y + Point.Y};
		const bool bSelected = Index == SelectedBoneIndex;
		const ImU32 Color = bSelected ? IM_COL32(255, 195, 40, 255) : IM_COL32(100, 215, 255, 210);
		FVector2 ParentPoint;
		if (Viewport.ProjectBone(Mesh->MeshData.Bones[Index].ParentIndex, DisplaySize, ParentPoint, Depth))
			DrawList->AddLine(Screen, ImVec2(Origin.x + ParentPoint.X, Origin.y + ParentPoint.Y), Color, bSelected ? 2.0f : 1.0f);
		DrawList->AddCircleFilled(Screen, bSelected ? 5.0f : 3.0f, Color);
		if (bSelected)
			DrawList->AddText(ImVec2(Screen.x + 8.0f, Screen.y - 8.0f), Color, Mesh->MeshData.Bones[Index].Name.c_str());
	}
	DrawList->PopClipRect();
}

void FSkeletalMeshViewerPanel::DrawDetails()
{
	const USkeletalMesh* Mesh = GetMesh();
	if (!Mesh) return;
	ImGui::TextUnformatted("Mesh Details");
	ImGui::Separator();
	ImGui::Text("Bones: %d", Mesh->MeshData.Bones.Num());
	ImGui::Text("Parts: %d", Mesh->MeshData.Meshes.Num());
	ImGui::Text("Materials: %d", Mesh->Materials.Num());
	ImGui::Separator();
	int32 SelectedBoneIndex = GetSelectedBoneIndex();
	const char* SelectedName = Mesh->MeshData.Bones.IsValidIndex(SelectedBoneIndex)
		? Mesh->MeshData.Bones[SelectedBoneIndex].Name.c_str() : "Select a bone";
	ImGui::BeginDisabled(Viewport.IsDraggingGizmo());
	if (ImGui::BeginCombo("Bone", SelectedName))
	{
		for (int32 Index = 0; Index < Mesh->MeshData.Bones.Num(); ++Index)
		{
			ImGui::PushID(Index);
			if (ImGui::Selectable(Mesh->MeshData.Bones[Index].Name.c_str(), Index == SelectedBoneIndex)) SetSelectedBoneIndex(Index);
			if (Index == SelectedBoneIndex) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	SelectedBoneIndex = GetSelectedBoneIndex();
	if (Mesh->MeshData.Bones.IsValidIndex(SelectedBoneIndex))
	{
		ImGui::Text("Selected bone: %d", SelectedBoneIndex);
		ImGui::TextWrapped("%s", Mesh->MeshData.Bones[SelectedBoneIndex].Name.c_str());
		if (const FTransform* Current = Viewport.GetSelectedBoneTransform())
		{
			FTransform Transform = *Current;
			ImGui::TextUnformatted("Parent-relative Transform");
			bool bChanged = ImGui::DragFloat3("Location", &Transform.Location.X, 0.01f);
			bChanged |= ImGui::DragFloat3("Rotation", &Transform.Rotation.Pitch, 0.25f);
			bChanged |= ImGui::DragFloat3("Scale", &Transform.Scale.X, 0.01f, 0.01f, 100.0f);
			if (bChanged) Viewport.SetSelectedBoneTransform(Transform);
		}
	}
	else ImGui::TextUnformatted("No bone selected.");
	ImGui::EndDisabled();
}

void FSkeletalMeshViewerPanel::RenderPreview(FRenderer& Renderer)
{
	// 직전 UI 프레임에서 실제 보인 경우에만 별도의 오프스크린 패스를 실행한다.
	if (IsOpen() && bPreviewVisible) Viewport.Render(Renderer);
	else Viewport.CancelGizmoDrag();
}

void FSkeletalMeshViewerPanel::ReleaseResources()
{
	Viewport.ReleaseResources();
	RebuildHierarchy();
	bPreviewVisible = false;
}
