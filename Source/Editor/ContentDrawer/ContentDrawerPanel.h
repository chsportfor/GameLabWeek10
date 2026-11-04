#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Core/EngineString.h"

#include <filesystem>
#include <functional>
#include <utility>

namespace fs = std::filesystem;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);
	// 에셋을 여는 UI는 호출부가 결정한다. Content Drawer는 뷰어를 소유하지 않는다.
	void SetOpenSkeletalMeshCallback(std::function<void(const FString&)> Callback) { OnOpenSkeletalMesh = std::move(Callback); }

private:
	// 사용자 콘텐츠(Assets)만 보여준다. 엔진 내장 파일(Resources)은 여기서 다루지 않는다.
	fs::path AssetRootPath = EditorResourcePaths::AssetRoot;
	fs::path CurrentPath = EditorResourcePaths::AssetRoot;
	std::function<void(const FString&)> OnOpenSkeletalMesh;

};
