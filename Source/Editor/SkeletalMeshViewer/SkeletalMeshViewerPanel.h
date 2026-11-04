#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerViewport.h"
#include "Core/EngineString.h"

// UI의 수명은 EditorUI가 관리한다. Content Drawer는 OpenAsset 요청만 전달한다.
class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }

	bool OpenAsset(const FString& Path);
	void RenderPreview(FRenderer& Renderer);
	void ReleaseResources();
	USkeletalMesh* GetMesh() const { return Viewport.GetMesh(); }
	int32 GetSelectedBoneIndex() const { return Viewport.GetSelectedBoneIndex(); }
	void SetSelectedBoneIndex(int32 BoneIndex);

private:
	void DrawHierarchy();
	void RebuildHierarchy();
	void DrawBoneNode(int32 BoneIndex, bool bScrollToSelection);
	void DrawViewport();
	void DrawDetails();
	void DrawBoneOverlay(const ImVec2& Origin, const ImVec2& Size);

	FSkeletalMeshViewerViewport Viewport;
	FString AssetPath;
	FString LoadError;
	// 에셋을 열 때 한 번 구성한다. 트리를 그릴 때 전체 본을 반복 검색하지 않는다.
	TArray<TArray<int32>> BoneChildren;
	TArray<int32> RootBones;
	TArray<int32> HierarchyParents;
	TArray<uint8> ExpandedBones;
	int32 LastHierarchySelectedBone = -1;
	int32 InvalidParentCount = 0;
	bool bShowBones = true;
	bool bRequestFocus = false;
	bool bPreviewVisible = false;
};
