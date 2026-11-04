#pragma once

#include "Core/EngineString.h"
#include "Containers/Map.h"
#include <functional>

class FAssetEditorSubsystem
{
public:
	using FAssetOpenHandler = std::function<void(const FString& AssetPath)>;

	static FAssetEditorSubsystem& Get()
	{
		static FAssetEditorSubsystem Instance;
		return Instance;
	}

	// 에셋 확장자(예: ".fbx", ".mat" 등)별로 에디터/뷰어를 여는 핸들러를 등록한다.
	void RegisterHandler(const FString& Extension, FAssetOpenHandler Handler);

	// 등록된 핸들러를 해제한다.
	void UnregisterHandler(const FString& Extension);

	// 파일 경로의 확장자에 맞는 핸들러를 찾아 실행한다.
	bool OpenAsset(const FString& AssetPath);

private:
	FAssetEditorSubsystem() = default;
	~FAssetEditorSubsystem() = default;

	FAssetEditorSubsystem(const FAssetEditorSubsystem&) = delete;
	FAssetEditorSubsystem& operator=(const FAssetEditorSubsystem&) = delete;

	static FString NormalizeExtension(const FString& InExt);

	TMap<FString, FAssetOpenHandler> Handlers;
};
