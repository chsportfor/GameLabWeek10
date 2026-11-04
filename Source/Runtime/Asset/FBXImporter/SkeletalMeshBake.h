#pragma once

#include <filesystem>
#include <optional>

struct FFbxCookedData;

class FSkeletalMeshBake
{
public:
	static bool ReadBaked(const FString& SourcePath, std::optional<std::filesystem::file_time_type> ExpectedSourceTime, FFbxCookedData& OutData);
	static void WriteBaked(const FString& SourcePath, std::filesystem::file_time_type SourceTime, const FFbxCookedData& Data);
};
