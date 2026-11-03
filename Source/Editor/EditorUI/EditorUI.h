#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/LevelEditor/LevelEditorToolbar.h"
#include <functional>

enum class EEditorMode
{
	LevelEditor,
	SkeletalMeshViewer
};

class FEditorUI
{
public:
	bool Init(bool bInUseDockSpace = true, bool bInPassthruCentralNode = false);
	void Tick(float DeltaTime);
	void OnRender();

	template <typename T>
	T* AddEditorPanel()
	{
		TUniquePtr<T> newPanel = MakeUnique<T>();
		T* Ret = newPanel.get();
		Ret->Init();
		Panels.Add(std::move(newPanel));
		
		return Ret;
	}

	FLevelEditorToolbar* GetToolbar() const { return Toolbar.get(); }

	EEditorMode GetEditorMode() const { return CurrentMode; }
	void SetEditorMode(EEditorMode InMode);
	void SetEditorModeChangedCallback(std::function<void(EEditorMode)> InCallback) { OnEditorModeChanged = std::move(InCallback); }

	void SetNewSceneCallback(std::function<void()> InCallback) { OnNewScene = InCallback; }
	void SetOpenSceneCallback(std::function<void()> InCallback) { OnOpenScene = InCallback; }
	void SetSaveSceneCallback(std::function<void()> InCallback) { OnSaveScene = InCallback; }
	void SetSaveSceneAsCallback(std::function<void()> InCallback) { OnSaveSceneAs = InCallback; }

	void ResetLevelLayout() { bResetLevelLayout = true; }

private:
	void DrawMainMenuBar();
	void DrawLevelEditorWorkspace(const struct ImGuiWindowClass& WindowClass);

private:
	bool bUseDockSpace = true;
	bool bPassthruCentralNode = false;

	EEditorMode CurrentMode = EEditorMode::LevelEditor;
	bool bPendingTabSelect = false;
	bool bResetLevelLayout = false;
	std::function<void(EEditorMode)> OnEditorModeChanged;

	TArray<TUniquePtr<IEditorPanel>> Panels;
	TUniquePtr<FLevelEditorToolbar> Toolbar;

	std::function<void()> OnNewScene;
	std::function<void()> OnOpenScene;
	std::function<void()> OnSaveScene;
	std::function<void()> OnSaveSceneAs;
};
