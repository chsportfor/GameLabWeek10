#include "EnginePCH.h"
#include "AssetEditorSubsystem.h"
#include "Core/EngineLog.h"

#include <filesystem>
#include <cctype>

FString FAssetEditorSubsystem::NormalizeExtension(const FString& InExt)
{
	FString Result = InExt;
	if (!Result.empty() && Result[0] != '.')
	{
		Result = "." + Result;
	}
	for (char& c : Result)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return Result;
}

void FAssetEditorSubsystem::RegisterHandler(const FString& Extension, FAssetOpenHandler Handler)
{
	const FString NormExt = NormalizeExtension(Extension);
	Handlers[NormExt] = std::move(Handler);
	HTR_LOG(Info, "[AssetEditorSubsystem] Registered handler for extension '{}'", NormExt);
}

void FAssetEditorSubsystem::UnregisterHandler(const FString& Extension)
{
	const FString NormExt = NormalizeExtension(Extension);
	Handlers.Remove(NormExt);
}

bool FAssetEditorSubsystem::OpenAsset(const FString& AssetPath)
{
	namespace fs = std::filesystem;
	std::string Ext = fs::path(AssetPath).extension().string();
	const FString NormExt = NormalizeExtension(Ext);

	auto* Found = Handlers.Find(NormExt);
	if (Found && *Found)
	{
		HTR_LOG(Info, "[AssetEditorSubsystem] Opening asset '{}' with registered handler for '{}'", AssetPath, NormExt);
		(*Found)(AssetPath);
		return true;
	}

	HTR_LOG(Warning, "[AssetEditorSubsystem] No asset editor registered for extension '{}' (Path: '{}')", NormExt, AssetPath);
	return false;
}
