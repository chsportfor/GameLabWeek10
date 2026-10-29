#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"

#include <filesystem>

namespace fs = std::filesystem;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);

private:
	// 사용자 콘텐츠(Assets)만 보여준다. 엔진 내장 파일(Resources)은 여기서 다루지 않는다.
	fs::path AssetRootPath = EditorResourcePaths::AssetRoot;
	fs::path CurrentPath = EditorResourcePaths::AssetRoot;

};